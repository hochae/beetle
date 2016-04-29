from __future__ import unicode_literals

from django.db import models

# Create your models here.

class Contact(models.Model):
	"""
	A human user with contact information.
	"""
	class Meta:
		unique_together = (("first_name", "last_name"),)

	NULL = 0

	first_name = models.CharField(
		max_length=100)
	last_name = models.CharField(
		max_length=100, 
		blank=True)
	phone_number = models.CharField(
		max_length=100)
	email_address = models.CharField(
		max_length=100, 
		blank=True)
	
	def __unicode__(self):
		return self.first_name + " " + self.last_name

class Principal(models.Model):
	""" 
	An application or peripheral device, using GATT
	"""
	
	class Meta:
		verbose_name = "GATT principal"
		verbose_name_plural = "GATT principals"

	# allowed types
	APP = "app"
	DEVICE = "device"
	UNKNOWN = "unknown"
	TYPE_CHOICES = (
		(APP, "app"),
		(DEVICE, "device"),
		(UNKNOWN, "unknown"),
	)

	name = models.CharField(
		max_length=100, 
		primary_key=True)
	ptype = models.CharField(
		max_length=20, 
		choices=TYPE_CHOICES,
		default=UNKNOWN)	
	verified = models.BooleanField(
		default=False,
		help_text="Has this principal been verified by a human?")

	owner = models.ForeignKey("Contact", default=Contact.NULL)

	def __unicode__(self):
		return self.name

class Gateway(models.Model):
	""" 
	A gateway in the network, serving as a GATT translator
	"""

	class Meta:
		verbose_name = "Gateway"
		verbose_name_plural = "Gateways"

	# allowed types
	ANDROID = "android"
	LINUX = "linux"
	UNKNOWN = "unknown"
	OS_CHOICES = (
		(ANDROID, "android"),
		(LINUX, "linux"),
		(UNKNOWN, "unknown"),
	)

	name = models.CharField(
		max_length=20, 
		primary_key=True)
	os = models.CharField(
		max_length=20, 
		default=LINUX, 
		choices=OS_CHOICES)
	trusted = models.BooleanField(default=True)

	def __unicode__(self):
		return self.name